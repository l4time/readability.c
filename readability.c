#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <curl/curl.h>
#include <time.h>
#include <sys/resource.h>
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <libxml/tree.h>
#include <libxml/uri.h>


#define MAX_CANDIDATES 1000
#define MAX_BUFFER 8192

// A struct to hold the downloaded HTML content
struct MemoryStruct
{
    char *memory;
    size_t size;
};

// A struct to hold candidate information
typedef struct
{
    xmlNodePtr node;
    double content_score;
} candidate_t;

// Callback function for libcurl to write data
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL)
    {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// Function to fetch HTML content from a URL
char *fetch_url(const char *url)
{
    CURL *curl_handle;
    CURLcode res;

    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK)
    {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        free(chunk.memory);
        chunk.memory = NULL;
    }

    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    return chunk.memory;
}

// Function to remove scripts, styles, and other unwanted tags
void remove_unwanted_tags(xmlNode *node)
{
    xmlNode *cur_node = NULL;
    for (cur_node = node; cur_node; cur_node = cur_node->next)
    {
        if (cur_node->type == XML_ELEMENT_NODE)
        {
            if (xmlStrcasecmp(cur_node->name, (const xmlChar *)"script") == 0 ||
                xmlStrcasecmp(cur_node->name, (const xmlChar *)"style") == 0 ||
                xmlStrcasecmp(cur_node->name, (const xmlChar *)"noscript") == 0 ||
                xmlStrcasecmp(cur_node->name, (const xmlChar *)"iframe") == 0 ||
                xmlStrcasecmp(cur_node->name, (const xmlChar *)"ad") == 0 ||
                xmlStrcasecmp(cur_node->name, (const xmlChar *)"ins") == 0 ||
                xmlStrcasecmp(cur_node->name, (const xmlChar *)"aside") == 0 ||
                xmlStrcasecmp(cur_node->name, (const xmlChar *)"figure") == 0)
            {
                xmlUnlinkNode(cur_node);
                xmlFreeNode(cur_node);
            }
            else
            {
                remove_unwanted_tags(cur_node->children);
            }
        }
    }
}

// Function to clean whitespace
void clean_whitespace(xmlChar *content)
{
    xmlChar *write = content, *read = content;
    int space = 0;
    while (*read)
    {
        if (*read == ' ' || *read == '\n' || *read == '\t')
        {
            if (!space)
            {
                *write++ = ' ';
                space = 1;
            }
        }
        else
        {
            *write++ = *read;
            space = 0;
        }
        read++;
    }
    *write = '\0';
}

// Function to get link density of a node
double get_link_density(xmlNode *node)
{
    xmlXPathContextPtr xpathCtx = xmlXPathNewContext(node->doc);
    xmlXPathObjectPtr xpathObj = xmlXPathNodeEval(node, (xmlChar *)".//a", xpathCtx);

    if (xpathObj == NULL)
    {
        xmlXPathFreeContext(xpathCtx);
        return 0.0;
    }

    xmlNodeSetPtr nodes = xpathObj->nodesetval;
    int link_length = 0, text_length = 0;

    for (int i = 0; i < nodes->nodeNr; i++)
    {
        xmlChar *link_text = xmlNodeGetContent(nodes->nodeTab[i]);
        link_length += strlen((char *)link_text);
        xmlFree(link_text);
    }

    xmlChar *node_text = xmlNodeGetContent(node);
    text_length = strlen((char *)node_text);
    xmlFree(node_text);

    double link_density = (text_length == 0) ? 0.0 : (double)link_length / text_length;

    xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx);
    return link_density;
}

// Function to get class weight of a node
int get_class_weight(xmlNode *node)
{
    int weight = 0;
    xmlChar *class = xmlGetProp(node, (xmlChar *)"class");
    xmlChar *id = xmlGetProp(node, (xmlChar *)"id");

    regex_t positive_regex, negative_regex;
    regcomp(&positive_regex, "article|body|content|entry|hentry|h-entry|main|page|pagination|post|text|blog|story", REG_ICASE | REG_EXTENDED);
    regcomp(&negative_regex, "hidden|^hid$| hid$| hid |^hid |banner|combx|comment|com-|contact|foot|footer|footnote|masthead|media|meta|outbrain|promo|related|scroll|share|shoutbox|sidebar|skyscraper|sponsor|shopping|tags|tool|widget|ad|advert|sponsor|promoted|recommended|paid|partnership", REG_ICASE | REG_EXTENDED);

    if (class)
    {
        if (regexec(&negative_regex, (char *)class, 0, NULL, 0) == 0)
            weight -= 25;
        if (regexec(&positive_regex, (char *)class, 0, NULL, 0) == 0)
            weight += 25;
    }

    if (id)
    {
        if (regexec(&negative_regex, (char *)id, 0, NULL, 0) == 0)
            weight -= 25;
        if (regexec(&positive_regex, (char *)id, 0, NULL, 0) == 0)
            weight += 25;
    }

    regfree(&positive_regex);
    regfree(&negative_regex);

    xmlFree(class);
    xmlFree(id);
    return weight;
}

// Function to initialize a node with content score
candidate_t *initialize_node(xmlNode *node)
{
    candidate_t *candidate = malloc(sizeof(candidate_t));
    candidate->node = node;
    candidate->content_score = 0;

    if (xmlStrcasecmp(node->name, (const xmlChar *)"div") == 0)
    {
        candidate->content_score += 5;
    }
    else if (xmlStrcasecmp(node->name, (const xmlChar *)"pre") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"td") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"blockquote") == 0)
    {
        candidate->content_score += 3;
    }
    else if (xmlStrcasecmp(node->name, (const xmlChar *)"address") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"ol") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"ul") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"dl") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"dd") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"dt") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"li") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"form") == 0)
    {
        candidate->content_score -= 3;
    }
    else if (xmlStrcasecmp(node->name, (const xmlChar *)"h1") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"h2") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"h3") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"h4") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"h5") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"h6") == 0 ||
             xmlStrcasecmp(node->name, (const xmlChar *)"th") == 0)
    {
        candidate->content_score -= 5;
    }

    return candidate;
}

// Function to extract the title
char *get_article_title(xmlDocPtr doc)
{
    xmlChar *title = NULL;
    xmlNode *root = xmlDocGetRootElement(doc);
    if (root)
    {
        xmlNode *head = root->children;
        while (head)
        {
            if (xmlStrcasecmp(head->name, (const xmlChar *)"head") == 0)
            {
                xmlNode *child = head->children;
                while (child)
                {
                    if (xmlStrcasecmp(child->name, (const xmlChar *)"title") == 0)
                    {
                        title = xmlNodeGetContent(child);
                        break;
                    }
                    child = child->next;
                }
            }
            if (title)
                break;
            head = head->next;
        }
    }
    return (char *)title;
}

// Function to extract metadata
char *get_metadata(xmlDocPtr doc, const char *property)
{
    xmlXPathContextPtr xpathCtx = xmlXPathNewContext(doc);
    char xpath[MAX_BUFFER];
    snprintf(xpath, sizeof(xpath), "//meta[@property='%s']/@content", property);
    xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression((xmlChar *)xpath, xpathCtx);

    char *result = NULL;
    if (xpathObj && xpathObj->nodesetval && xpathObj->nodesetval->nodeNr > 0)
    {
        xmlNodePtr node = xpathObj->nodesetval->nodeTab[0];
        result = (char *)xmlNodeGetContent(node);
    }

    xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx);
    return result;
}

// Helper function to count occurrences of a substring
int xmlStrCount(const xmlChar *str, const xmlChar *substr) {
    int count = 0;
    const xmlChar *tmp = str;
    while((tmp = xmlStrstr(tmp, substr))) {
        count++;
        tmp++;
    }
    return count;
}

// extract_article_content function
void extract_article_content(xmlNode *body, xmlNode **article_content)
{
    xmlXPathContextPtr xpathCtx = xmlXPathNewContext(body->doc);
    xmlXPathObjectPtr xpathObj = xmlXPathNodeEval(body, (xmlChar *)"//body//p|//body//td|//body//pre", xpathCtx);

    if (xpathObj == NULL)
    {
        xmlXPathFreeContext(xpathCtx);
        return;
    }

    xmlNodeSetPtr nodes = xpathObj->nodesetval;
    int size = (nodes) ? nodes->nodeNr : 0;
    candidate_t *top_candidate = NULL;
    candidate_t *candidates[MAX_CANDIDATES];
    int candidate_count = 0;

    for (int i = 0; i < size; i++)
    {
        xmlNode *elem = nodes->nodeTab[i];
        xmlChar *content = xmlNodeGetContent(elem);
        if (xmlStrlen(content) < 25)
        {
            xmlFree(content);
            continue;
        }

        xmlNode *parent_node = elem->parent;
        xmlNode *grand_parent_node = parent_node ? parent_node->parent : NULL;

        if (!parent_node || !grand_parent_node)
        {
            xmlFree(content);
            continue;
        }

        candidate_t *parent_candidate = NULL;
        candidate_t *grand_parent_candidate = NULL;

        for (int j = 0; j < candidate_count; j++)
        {
            if (candidates[j]->node == parent_node)
            {
                parent_candidate = candidates[j];
            }
            if (candidates[j]->node == grand_parent_node)
            {
                grand_parent_candidate = candidates[j];
            }
        }

        if (!parent_candidate && candidate_count < MAX_CANDIDATES)
        {
            parent_candidate = initialize_node(parent_node);
            candidates[candidate_count++] = parent_candidate;
        }
        if (!grand_parent_candidate && candidate_count < MAX_CANDIDATES)
        {
            grand_parent_candidate = initialize_node(grand_parent_node);
            candidates[candidate_count++] = grand_parent_candidate;
        }

        if (parent_candidate && grand_parent_candidate)
        {
            int content_score = 1;
            content_score += xmlStrlen(content) / 100;
            content_score += xmlStrCount(content, (xmlChar *)",") * 3;

            parent_candidate->content_score += content_score;
            grand_parent_candidate->content_score += content_score / 2.0;

            if (xmlStrcasecmp(elem->name, (xmlChar *)"p") == 0)
            {
                parent_candidate->content_score += 5;
                grand_parent_candidate->content_score += 3;
            }
        }

        xmlFree(content);
    }

    for (int i = 0; i < candidate_count; i++)
    {
        candidates[i]->content_score *= (1 - get_link_density(candidates[i]->node));
        candidates[i]->content_score += get_class_weight(candidates[i]->node);
        if (!top_candidate || candidates[i]->content_score > top_candidate->content_score)
        {
            top_candidate = candidates[i];
        }
    }

    if (!top_candidate)
    {
        xmlXPathFreeObject(xpathObj);
        xmlXPathFreeContext(xpathCtx);
        for (int i = 0; i < candidate_count; i++)
        {
            free(candidates[i]);
        }
        return;
    }

    *article_content = xmlNewNode(NULL, (xmlChar *)"div");
    xmlNode *sibling = top_candidate->node->parent->children;
    while (sibling)
    {
        if (sibling->type == XML_ELEMENT_NODE)
        {
            int append = 0;
            candidate_t *sibling_candidate = NULL;

            for (int i = 0; i < candidate_count; i++)
            {
                if (candidates[i]->node == sibling)
                {
                    sibling_candidate = candidates[i];
                    break;
                }
            }

            if (sibling == top_candidate->node)
            {
                append = 1;
            }
            else if (sibling_candidate && sibling_candidate->content_score >= top_candidate->content_score * 0.2)
            {
                append = 1;
            }
            else if (xmlStrcasecmp(sibling->name, (const xmlChar *)"p") == 0)
            {
                double link_density = get_link_density(sibling);
                xmlChar *node_text = xmlNodeGetContent(sibling);
                int text_length = xmlStrlen(node_text);
                xmlFree(node_text);

                if (text_length > 80 && link_density < 0.25)
                {
                    append = 1;
                }
                else if (text_length < 80 && link_density == 0 && xmlStrstr(node_text, (xmlChar *)"."))
                {
                    append = 1;
                }
            }

            if (append)
            {
                xmlNode *node_to_append = xmlCopyNode(sibling, 1);
                xmlAddChild(*article_content, node_to_append);
            }
        }
        sibling = sibling->next;
    }

    xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx);
    for (int i = 0; i < candidate_count; i++)
    {
        free(candidates[i]);
    }
}

// Function to convert HTML to Markdown-like text
void html_to_markdown(xmlNode *node, FILE *output_file, int depth)
{
    xmlNode *current_node = NULL;
    for (current_node = node; current_node; current_node = current_node->next)
    {
        if (current_node->type == XML_ELEMENT_NODE)
        {
            if (xmlStrcasecmp(current_node->name, (const xmlChar *)"p") == 0)
            {
                fprintf(output_file, "\n");
            }
            else if (xmlStrcasecmp(current_node->name, (const xmlChar *)"br") == 0)
            {
                fprintf(output_file, "\n");
            }
            else if (xmlStrcasecmp(current_node->name, (const xmlChar *)"h1") == 0)
            {
                fprintf(output_file, "\n# ");
            }
            else if (xmlStrcasecmp(current_node->name, (const xmlChar *)"h2") == 0)
            {
                fprintf(output_file, "\n## ");
            }
            else if (xmlStrcasecmp(current_node->name, (const xmlChar *)"h3") == 0)
            {
                fprintf(output_file, "\n### ");
            }
            else if (xmlStrcasecmp(current_node->name, (const xmlChar *)"h4") == 0)
            {
                fprintf(output_file, "\n#### ");
            }
            else if (xmlStrcasecmp(current_node->name, (const xmlChar *)"h5") == 0)
            {
                fprintf(output_file, "\n##### ");
            }
            else if (xmlStrcasecmp(current_node->name, (const xmlChar *)"h6") == 0)
            {
                fprintf(output_file, "\n###### ");
            }
            else if (xmlStrcasecmp(current_node->name, (const xmlChar *)"a") == 0)
            {
                xmlChar *href = xmlGetProp(current_node, (const xmlChar *)"href");
                if (href)
                {
                    fprintf(output_file, "[");
                    html_to_markdown(current_node->children, output_file, depth + 1);
                    fprintf(output_file, "](%s)", href);
                    xmlFree(href);
                    continue;
                }
            }
            else if (xmlStrcasecmp(current_node->name, (const xmlChar *)"ul") == 0 ||
                     xmlStrcasecmp(current_node->name, (const xmlChar *)"ol") == 0)
            {
                html_to_markdown(current_node->children, output_file, depth + 1);
                fprintf(output_file, "\n");
                continue;
            }
            else if (xmlStrcasecmp(current_node->name, (const xmlChar *)"li") == 0)
            {
                fprintf(output_file, "\n%*s- ", depth * 2, "");
            }
            else if (xmlStrcasecmp(current_node->name, (const xmlChar *)"strong") == 0 ||
                     xmlStrcasecmp(current_node->name, (const xmlChar *)"b") == 0)
            {
                fprintf(output_file, "**");
                html_to_markdown(current_node->children, output_file, depth + 1);
                fprintf(output_file, "**");
                continue;
            }
            else if (xmlStrcasecmp(current_node->name, (const xmlChar *)"em") == 0 ||
                     xmlStrcasecmp(current_node->name, (const xmlChar *)"i") == 0)
            {
                fprintf(output_file, "*");
                html_to_markdown(current_node->children, output_file, depth + 1);
                fprintf(output_file, "*");
                continue;
            }

            html_to_markdown(current_node->children, output_file, depth + 1);

            if (xmlStrcasecmp(current_node->name, (const xmlChar *)"p") == 0)
            {
                fprintf(output_file, "\n");
            }
        }
        else if (current_node->type == XML_TEXT_NODE)
        {
            xmlChar *content = xmlNodeGetContent(current_node);
            if (content)
            {
                clean_whitespace(content);
                fprintf(output_file, "%s", content);
                xmlFree(content);
            }
        }
    }
}

// Function to extract metadata and article content, and print to console or JSON
void extract_article(xmlDocPtr doc, const char *url, int json_output)
{
    char *title = get_article_title(doc);
    char *author = get_metadata(doc, "og:author");
    char *description = get_metadata(doc, "og:description");
    char *site_name = get_metadata(doc, "og:site_name");
    char *published_time = get_metadata(doc, "article:published_time");

    if (json_output)
    {
        printf("{\n");
        printf("  \"title\": \"%s\",\n", title ? title : "");
        printf("  \"url\": \"%s\",\n", url);
        printf("  \"publishedTime\": \"%s\",\n", published_time ? published_time : "");
        printf("  \"content\": \"");
    }
    else
    {
        if (title) printf("Title: %s\n\n", title);
        if (author) printf("Author: %s\n\n", author);
        if (description) printf("Description: %s\n\n", description);
        if (site_name) printf("Site Name: %s\n\n", site_name);
        printf("URL Source: %s\n\n", url);
        if (published_time) printf("Published Time: %s\n\n", published_time);
        printf("Markdown Content:\n");
    }

    xmlNode *body = xmlDocGetRootElement(doc);
    if (body)
    {
        remove_unwanted_tags(body);
        xmlNode *article_content = NULL;
        extract_article_content(body, &article_content);
        if (article_content)
        {
            FILE *temp_file = tmpfile();
            if (temp_file)
            {
                html_to_markdown(article_content, temp_file, 0);
                fseek(temp_file, 0, SEEK_SET);
                int c;
                while ((c = fgetc(temp_file)) != EOF)
                {
                    if (json_output && c == '"') putchar('\\');
                    if (json_output && c == '\n') printf("\\n");
                    else putchar(c);
                }
                fclose(temp_file);
            }
            xmlFreeNode(article_content);
        }
        else
        {
            fprintf(stderr, "Error: Failed to extract article content\n");
        }
    }

    if (json_output)
    {
        printf("\"\n}\n");
    }

    if (title) xmlFree(title);
    if (author) free(author);
    if (description) free(description);
    if (site_name) free(site_name);
    if (published_time) free(published_time);
}

int main(int argc, char **argv)
{
    clock_t start_time = clock();
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "Usage: %s <url> [-json]\n", argv[0]);
        return 1;
    }

    const char *url = argv[1];
    int json_output = (argc == 3 && strcmp(argv[2], "-json") == 0);

    char *html_content = fetch_url(url);

    if (html_content)
    {
        htmlDocPtr doc = htmlReadMemory(html_content, strlen(html_content), NULL, NULL, HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);

        if (doc == NULL)
        {
            fprintf(stderr, "Error: unable to parse HTML\n");
            free(html_content);
            return 1;
        }

        extract_article(doc, url, json_output);

        xmlFreeDoc(doc);
        free(html_content);

        if (!json_output)
        {
            printf("\n\nArticle extracted\n");
        }
    }
    else
    {
        fprintf(stderr, "Error: unable to fetch URL\n");
        return 1;
    }

    if (!json_output)
    {
        clock_t end_time = clock();
        double execution_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        printf("Execution time: %f seconds\n", execution_time);

        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        printf("Memory usage: %ld MB\n", usage.ru_maxrss / 1024);
    }

    return 0;
}